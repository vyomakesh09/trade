import numpy as np
from hftbacktest import HftBacktest, Stat
from bitmex_hft_bot import BitMEXHFTBot

def bitmex_hft_strategy(hbt, stat):
    bot = BitMEXHFTBot()
    
    while hbt.elapse(100_000):  # Check every 0.1s
        hbt.clear_inactive_orders()

        # Get current market data
        mid_price = (hbt.best_bid + hbt.best_ask) / 2.0
        
        # Create a dictionary to mimic the structure expected by the C++ bot
        market_data = {
            'price': mid_price,
            'volume': hbt.get_volume(),
            'best_bid': hbt.best_bid,
            'best_ask': hbt.best_ask
        }
        
        # Call the C++ bot's trade function
        bot.trade(market_data)
        
        # Handle orders
        for order in hbt.orders.values():
            if order.cancellable:
                hbt.cancel(order.order_id)
        
        # Place new orders based on the bot's decision
        new_orders = bot.get_desired_orders()
        for order in new_orders:
            if order['side'] == 'Buy':
                hbt.submit_buy_order(order['id'], order['price'], order['quantity'], 'GTX')
            else:
                hbt.submit_sell_order(order['id'], order['price'], order['quantity'], 'GTX')
        
        # Wait for order responses
        if new_orders:
            hbt.wait_order_response(new_orders[-1]['id'])
        
        # Record stats
        stat.record(hbt)

# Load the prepared data
data = np.load('bitmex_formatted_data.npy')

# Initialize the backtest
hbt = HftBacktest(data, 
                  tick_size=0.5, 
                  lot_size=1, 
                  maker_fee=0.0002, 
                  taker_fee=0.0007)

# Run the backtest
stat = Stat()
bitmex_hft_strategy(hbt, stat)

# Print results
print(f"Total PnL: {stat.pnl[-1]}")
print(f"Sharpe Ratio: {stat.sharpe_ratio()}")
print(f"Max Drawdown: {stat.max_drawdown()}")