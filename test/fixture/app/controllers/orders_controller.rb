class OrdersController < ApplicationController
  def show
    @order = Order.new([])
    @total = @order.total
    render_badge
  end

  def preview
    send("format_#{params[:kind]}")
  end

  private

  def render_badge; end
  def unused_helper; end
end
