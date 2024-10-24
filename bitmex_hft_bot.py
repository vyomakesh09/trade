import numpy as np
from typing import Dict, List

class BitMEXHFTBot:
    def __init__(self):
        self.position = 0
        self.orders = []

    def trade(self, market_data: Dict[str, float]):
        # Simple trading logic based on price movement
        mid_price = market_data['price']
        volume = market_data['volume']
        
        # Calculate a simple moving average
        self.price_history = getattr(self, 'price_history', [])
        self.price_history.append(mid_price)
        if len(self.price_history) > 20:
            self.price_history.pop(0)
        
        sma = np.mean(self.price_history)
        
        # Generate orders based on price relative to SMA
        self.orders = []
        if mid_price > sma * 1.01:  # Price is 1% above SMA, consider selling
            self.orders.append({
                'id': 'sell_order',
                'side': 'Sell',
                'price': mid_price * 0.999,  # Slightly below current price
                'quantity': 100
            })
        elif mid_price < sma * 0.99:  # Price is 1% below SMA, consider buying
            self.orders.append({
                'id': 'buy_order',
                'side': 'Buy',
                'price': mid_price * 1.001,  # Slightly above current price
                'quantity': 100
            })

    def get_desired_orders(self) -> List[Dict]:
        return self.orders
